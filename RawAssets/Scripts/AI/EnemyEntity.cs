using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class EnemyEntity : MonoBehaviour
{    
    [SerializeField] private float range;
    [SerializeField] private float colliderDistance;
    [SerializeField] private BoxCollider2D boxCollider;
    [SerializeField] private LayerMask playerLayer;
    [SerializeField] private Animator anim;
    [SerializeField] private GameObject Player;    
    [SerializeField] private GameObject Enemy;
    [SerializeField] private float speed;
    [SerializeField] private float distanceBetween;
    [SerializeField] private float attackDistance;

    private Vector2 MoveAmount;
    
    private Rigidbody2D rb;
    private Vector3 startingPosition;
    private EnemyPatrol enemyPatrol;
    private bool FacingRight = true;


 private void Start() 
 {     
     startingPosition = transform.position;
     boxCollider = GetComponent<BoxCollider2D>();
     enemyPatrol = GetComponentInParent<EnemyPatrol>();
     rb = GetComponent<Rigidbody2D>();
 }

 private void Update()
 {   
     

     colliderDistance = Vector2.Distance(transform.position, Player.transform.position);
     Vector2 direction = Player.transform.position - transform.position;
     direction.Normalize();
       
     
     if(colliderDistance <= distanceBetween && colliderDistance > attackDistance)
    {
       Debug.Log("if");       
       enemyPatrol.enabled = false;
       transform.position = Vector2.MoveTowards(this.transform.position, Player.transform.position, speed * Time.deltaTime);
       
    }
    else if(colliderDistance <= attackDistance)
    {        
        anim.SetBool("EnemyAttack", true);
        enemyPatrol.enabled = false;
    }

    if (Player.transform.position.x < transform.position.x && FacingRight)
        {
            Flip();
        }
        else
        if (Player.transform.position.x > transform.position.x && !FacingRight)
        {
            Flip();
        }    

 }


 private void OnTriggerEnter2D(Collider2D collision)
 {
     if(collision.tag == "Attack")
     {   
        Debug.Log("if3"); 
        anim.SetTrigger("die");        
        StartCoroutine(DestroyCoroutine());
     }
     else if(collision.tag == "Player")
     {
         Debug.Log("if2");
         anim.SetBool("Move", false);
         anim.SetBool("EnemyAttack", true);
     }

 }
 private void OnTriggerStay2D(Collider2D col)
 {
     if(col.tag == "Player")
     {
         Debug.Log("if2");         
         anim.SetBool("Move", false);
         anim.SetBool("EnemyAttack", true);
     }
 }

 private void OnTriggerExit2D(Collider2D other)
 {
     Debug.Log("if4");
     anim.SetBool("EnemyAttack", false);     
     anim.SetBool("Move", true);
 }

 private IEnumerator DestroyCoroutine()
    {       
        yield return new WaitForSeconds(0.7f);        
        Destroy(Enemy);
    } 

 private void Flip()
 {
      Vector3 tmpScale = transform.localScale;
        tmpScale.x = - tmpScale.x;
        transform.localScale = tmpScale;
        FacingRight = !FacingRight;
 }
   

}
